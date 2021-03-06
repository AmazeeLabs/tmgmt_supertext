<?php

/**
 * @file
 * Provides the Supertext translation plugin controller.
 */

/**
 * Supertext translator plugin controller.
 */
class TMGMTSupertextPluginController extends TMGMTDefaultTranslatorPluginController {

  /**
   * Translation service URL.
   *
   * @var string
   */
  protected $apiURL = 'https://www.supertext.ch/api/v1/';

  /**
   * Translation service sandbox URL.
   *
   * @var string
   */
  protected $sandboxURL = 'http://dev.supertext.ch/api/v1/';

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::isAvailable().
   */
  public function isAvailable(TMGMTTranslator $translator) {
    return TRUE;
  }


  /**
   * Implements TMGMTTranslatorPluginControllerInterface::canTranslate().
   */
  public function canTranslate(TMGMTTranslator $translator, TMGMTJob $job) {
    // Checking if there is a mapping saved.
    if (!$translator->getSetting('language_mapping_' . $job->target_language)) {
      // If no mapping saved, we check if the language is directly supported by
      // Supertext (only 1 mapping language).
      $languages = language_list();
      $possible_mapping_languages = $this->getMappedLanguages($languages[$job->target_language], $translator);
      if (count($possible_mapping_languages) == 1) {
        return TRUE;
      }
      return FALSE;
    }
    return TRUE;
  }


  /**
   * Implements TMGMTTranslatorPluginControllerInterface::getNotCanTranslateReason().
   *
   * @todo Probably needs refactoring depending on http://drupal.org/node/1538762
   * and http://drupal.org/node/1420364
   */
  public function getNotCanTranslateReason(TMGMTJob $job) {
    $translator = $job->getTranslator();
    if (!$translator->getSetting('language_mapping_' . $job->target_language)) {
      return t('There is no language mapping for this target language configured.');
    }
    return parent::getNotCanTranslateReason($job);
  }


  /**
   * Returns a mapping for a generic language or null if there is no
   * mapping necessary
   */
  public function getMappedLanguages($language, TMGMTTranslator $translator) {
    $lastresponse = &drupal_static(__FUNCTION__);
    // If the last response was not 200, probably this one will also fail,
    // so we don't run it.
    /*if (isset($lastresponse->code) && $lastresponse->code != 200) {
      return FALSE;
    }*/
    $service = 'translation/languagemapping/' . $language->language;
    $response = $this->supertextHttpRequest($service, $translator, NULL, "GET");
    $lastresponse = $response;
    if ($response->code == "200") {
      $responsedata = json_decode($response->data);
      // If language is supported, we don't need a mapping.
      $lang_settings = array();
      if (isset($responsedata->Supported)) {
        if ($responsedata->Supported == TRUE) {
          $lang_settings[$language->language] = $language->name;
          return $lang_settings;
        }
        else {
          if (isset($responsedata->Languages)) {
            foreach ($responsedata->Languages as $lang_item) {
              $lang_settings[$lang_item->Iso] = $lang_item->Name;
            }
            return $lang_settings;
          }
        }
      }
    }
    else {
      drupal_set_message(t("Could not get language mapping for @language_name from Supertext. Error: @error<br />Check the log for more details.", array('@language_name' => $language->name, '@error' => $response->error)), 'error');
      return FALSE;
    }
  }

  /**
   * General function for making an HTTP Request to Supertext API
   *
   * @param string $service
   *   The service to be called, the API URL itself.
   *
   * @param TMGMTTranslator $translator
   *   Object of the translator which should be used.
   *
   * @param object $dataobject
   *   The object to be sent via POST.
   *
   * @param string $method
   *   With HTTP Method should be used. Default to POST.
   *
   * @return object
   *   Returns of drupal_http_request()
   */
  public function supertextHttpRequest($service, TMGMTTranslator $translator, $dataobject = NULL, $method = "POST") {
    // Allow to use the internal mocking URL.
    if ($custom_url = $translator->getSetting('url')) {
      $url = $custom_url;
    }
    else {
      $url = $translator->getSetting('use_sandbox') ? $this->sandboxURL : $this->apiURL;
    }
    $url .= $service;

    $options = array(
      'method' => $method,
      'headers' => array(
        'Content-Type' => 'application/json',
        'Accept' => 'application/json',
        'User-Agent' => 'Drupal Supertext Translation Interface v0.1',
        'Authorization' => 'Basic ' . base64_encode($translator->getSetting('api_username') . ':' . $translator->getSetting('api_token'))),
    );

    if (isset($dataobject)) {
      $options['data'] = json_encode($dataobject);
    }

    $response = drupal_http_request($url, $options);
    if ($response->code != '200' || $translator->getSetting('use_sandbox')) {
      watchdog('tmgmt_supertext', 'Request to Supertext @service.
<br />Response code: @response_code
<br />Response data: @response_data
<br />Request URL: @request_url
<br />Request method: @request_method
<br />Request data: <pre>@request_data</pre>', array(
        '@service' => $service,
        '@response_code' =>$response->code,
        '@response_data' =>$response->data,
        '@request_url' => $url,
        '@request_method' => $method,
        '@request_data' => isset($dataobject) ? print_r($dataobject, TRUE) : '',
      ), $response->code == '200' ? WATCHDOG_NOTICE : WATCHDOG_ERROR);
    }
    return $response;
  }

  /**
   *  Generates the Order Object needed by Supertext
   *
   *  @param TMGMTJob $job
   *   The TMGMT Job Object to make the Order for
   */
  public function generateSupertextOrderObject(TMGMTJob $job) {
    $translator = $job->getTranslator();
    // Check if there is mapping.
    if ($language = $translator->getSetting('language_mapping_' . $job->source_language)) {
      $sourcelanguage = $language;
    }
    else {
      $sourcelanguage = $job->source_language;
    }
    if ($language = $translator->getSetting('language_mapping_' . $job->target_language)) {
      $targetlanguage = $language;
    }
    else {
      $targetlanguage = $job->target_language;
    }
    $object = new stdClass();
    $object->CallbackUrl = url('tmgmt_supertext_callback', array('absolute' => TRUE));
    $object->ContentType = "text/html";
    $object->Currency = $translator->getSetting('currency');
    $object->SourceLang = $sourcelanguage;
    $object->TargetLang = $targetlanguage;
    $object->ReferenceData = $job->tjid . ':' . tmgmt_supertext_hash($job->tjid);
    $object->Groups = array();

    $items = $job->getItems();
    foreach ($items as $tjiid => $item) {
      $itemsourcedata = $item->getData();
      $fields = array_filter(tmgmt_flatten_data($itemsourcedata), '_tmgmt_filter_data');
      $group = new stdClass();
      $group->GroupId = $tjiid;
      $group->Context = isset($itemsourcedata['#label']) ? $itemsourcedata['#label'] : '';
      $group->Items = array();
      foreach ($fields as $fieldarray_id => $field_array) {
        $field = new stdClass();
        $field->Content = $field_array['#text'];
        $field->Context = isset($field_array['#label']) ? $field_array['#label'] : '';
        $field->Id = $fieldarray_id;
        $group->Items[] = $field;
      }
      $object->Groups[] = $group;
    }
    return $object;
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::requestTranslation().
   *
   * Here we will acutally query source and get translations.
   */
  public function requestTranslation(TMGMTJob $job) {
    $predefined_order_type_id = (string) $job->getTranslator()->getSetting('order_type_id');
    $object = $this->generateSupertextOrderObject($job);
    $order_settings = explode(":", $job->settings['supertext-job']);
    $object->OrderTypeId = ctype_digit($predefined_order_type_id) ? $predefined_order_type_id : $order_settings[0];
    $object->DeliveryId = $order_settings[1];
    $object->OrderName = trim($job->label);
    if ($object->OrderName === '') {
      $object->OrderName = 'Job ' . $job->tjid;
    }
    elseif (strlen($object->OrderName) > 100) {
      $object->OrderName = substr($object->OrderName, 0, 97) . '...';
    }

    $response = $this->supertextHttpRequest('translation/order', $job->getTranslator(), $object);
    if ($response->code == "200") {
      $responsedata = json_decode($response->data);
      // Save order id to the job.
      $job->reference = $responsedata->Id;
      $job->submitted("Thank you very much for your order. Order Id: @orderid. Deadline: @deadline.", array(
          '@orderid' => $responsedata->Id,
          '@deadline' => format_date(strtotime($responsedata->Deadline)),
        ));
    }
    elseif ($response->code == "401") {
      // Wrong or missing authentication.
      $job->rejected('Rejected by Supertext Translator: Wrong or missing authentication. Please check your Supertext Translator Settings. Check the log for more details.', array(), 'error');
      return;
    }
    else {
      $job->rejected('Unknown error from Supertext Translator: !error<br />Check the log for more details.', array('!error' => $response->data), 'error');
      return;
    }
  }

  /**
   * Implements
   * TMGMTTranslatorPluginControllerInterface::getSupportedTargetLanguages().
   */
  public function getSupportedTargetLanguages(TMGMTTranslator $translator, $source_language) {
    // @todo: Implement this properly.
    return parent::getSupportedTargetLanguages($translator, $source_language);
  }

}
